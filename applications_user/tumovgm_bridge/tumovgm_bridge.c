#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <expansion/expansion.h>

#include "protocol/protocol.h"

#define TAG "TumoVgmBridge"

enum {
    TumoVgmBridgeBaud = 230400,
    TumoVgmStockBaud = 9600,
    TumoVgmExchangeTimeoutMs = 500,
    TumoVgmStockProbeMs = 800,
    TumoVgmKeepaliveMs = 1000,
    TumoVgmLeaseMs = 3000,
    TumoVgmHelloPayloadSize = 12,
    TumoVgmDeviceInfoPayloadSize = 48,
    TumoVgmSessionOpenPayloadSize = 12,
    TumoVgmSessionOpenResponseSize = 16,
    TumoVgmSessionClosePayloadSize = 4,
    TumoVgmPingPayloadSize = 8,
    TumoVgmErrorPayloadSize = 4,
    TumoVgmRxStreamSize = TumovgmFrameMaxSize * 2,
};

typedef enum {
    TumoVgmStateScanning,
    TumoVgmStateMissing,
    TumoVgmStateStock,
    TumoVgmStateIncompatible,
    TumoVgmStateReady,
    TumoVgmStateSession,
    TumoVgmStateError,
} TumoVgmState;

typedef enum {
    TumoVgmWorkerFlagData = 1U << 0,
    TumoVgmWorkerFlagExit = 1U << 1,
    TumoVgmWorkerFlagStart = 1U << 2,
    TumoVgmWorkerFlagStop = 1U << 3,
    TumoVgmWorkerFlagRetry = 1U << 4,
} TumoVgmWorkerFlag;

#define TUMOVGM_WORKER_FLAGS                                                    \
    (TumoVgmWorkerFlagData | TumoVgmWorkerFlagExit | TumoVgmWorkerFlagStart | \
     TumoVgmWorkerFlagStop | TumoVgmWorkerFlagRetry)

typedef struct {
    TumoVgmState state;
    uint8_t protocol_major;
    uint8_t protocol_minor;
    uint16_t hardware_target;
    uint32_t session_id;
    uint64_t capabilities;
    bool dirty;
    char version[25];
    char commit[13];
    char detail[32];
} TumoVgmViewModel;

typedef struct {
    Gui* gui;
    Expansion* expansion;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriThread* worker;
    FuriThreadId worker_id;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    bool serial_started;
    bool exit_requested;
    uint16_t sequence;
    uint8_t tx_buffer[TumovgmFrameMaxSize];
    uint8_t rx_buffer[TumovgmFrameMaxSize];
    size_t rx_size;
    TumoVgmViewModel device;
} TumoVgmApp;

static uint16_t tumovgm_read_u16(const uint8_t* data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t tumovgm_read_u32(const uint8_t* data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint64_t tumovgm_read_u64(const uint8_t* data) {
    uint64_t value = 0;
    for(uint8_t index = 0; index < 8; index++) value |= (uint64_t)data[index] << (index * 8);
    return value;
}

static void tumovgm_write_u16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void tumovgm_write_u32(uint8_t* data, uint32_t value) {
    for(uint8_t index = 0; index < 4; index++) data[index] = (uint8_t)(value >> (index * 8));
}

static void tumovgm_copy_fixed_ascii(char* output, size_t output_size, const uint8_t* input, size_t input_size) {
    const size_t copy_size = MIN(output_size - 1, input_size);
    memcpy(output, input, copy_size);
    output[copy_size] = '\0';
    for(size_t index = 0; index < copy_size; index++) {
        if(output[index] == '\0') break;
        if((uint8_t)output[index] < 0x20 || (uint8_t)output[index] > 0x7E) output[index] = '?';
    }
}

static const char* tumovgm_state_name(TumoVgmState state) {
    switch(state) {
    case TumoVgmStateScanning:
        return "Scanning module";
    case TumoVgmStateMissing:
        return "Module not found";
    case TumoVgmStateStock:
        return "Stock VGM found";
    case TumoVgmStateIncompatible:
        return "Protocol mismatch";
    case TumoVgmStateReady:
        return "TumoVGM ready";
    case TumoVgmStateSession:
        return "Session active";
    case TumoVgmStateError:
        return "Connection lost";
    default:
        return "Unknown";
    }
}

static void tumovgm_refresh_view(TumoVgmApp* app) {
    with_view_model(
        app->view,
        TumoVgmViewModel * model,
        { *model = app->device; },
        true);
}

static void tumovgm_set_state(TumoVgmApp* app, TumoVgmState state, const char* detail) {
    app->device.state = state;
    strlcpy(app->device.detail, detail ? detail : "", sizeof(app->device.detail));
    tumovgm_refresh_view(app);
}

static void tumovgm_draw_callback(Canvas* canvas, void* context) {
    const TumoVgmViewModel* model = context;
    char line[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, "TumoVGM Bridge");
    canvas_draw_rframe(canvas, 2, 13, 124, 14, 3);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas, 64, 16, AlignCenter, AlignTop, tumovgm_state_name(model->state));

    if(model->state == TumoVgmStateReady || model->state == TumoVgmStateSession) {
        canvas_draw_str_aligned(
            canvas, 64, 28, AlignCenter, AlignTop, model->version[0] ? model->version : "Identity unavailable");
        snprintf(
            line,
            sizeof(line),
            "Proto %u.%u  %s",
            model->protocol_major,
            model->protocol_minor,
            model->hardware_target == TumovgmHardwareTargetVgmRp2040 ? "VGM RP2040" : "Unknown HW");
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignTop, line);
        if(model->state == TumoVgmStateSession) {
            snprintf(line, sizeof(line), "Session #%lu", (unsigned long)model->session_id);
        } else if(model->capabilities == 0) {
            snprintf(line, sizeof(line), "Caps: none%s", model->dirty ? " DIRTY" : "");
        } else {
            snprintf(
                line,
                sizeof(line),
                "Caps 0x%08lX%s",
                (unsigned long)model->capabilities,
                model->dirty ? " DIRTY" : "");
        }
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignTop, line);
    } else {
        canvas_draw_str_aligned(canvas, 64, 31, AlignCenter, AlignTop, model->detail);
        if(model->state == TumoVgmStateStock) {
            canvas_draw_str_aligned(canvas, 64, 43, AlignCenter, AlignTop, "Install TumoVGM UF2");
        }
    }

    elements_button_left(canvas, "Back");
    if(model->state == TumoVgmStateReady) {
        elements_button_center(canvas, "Start");
    } else if(model->state == TumoVgmStateSession) {
        elements_button_center(canvas, "Stop");
    } else if(model->state == TumoVgmStateMissing || model->state == TumoVgmStateStock ||
              model->state == TumoVgmStateIncompatible || model->state == TumoVgmStateError) {
        elements_button_center(canvas, "Retry");
    }
}

static uint32_t tumovgm_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static bool tumovgm_input_callback(InputEvent* event, void* context) {
    TumoVgmApp* app = context;
    if(event->type != InputTypeShort) return false;

    if(event->key == InputKeyLeft) {
        view_dispatcher_stop(app->view_dispatcher);
        return true;
    }
    if(event->key != InputKeyOk) return false;

    TumoVgmState state;
    with_view_model(app->view, TumoVgmViewModel * model, { state = model->state; }, false);
    uint32_t flag = 0;
    if(state == TumoVgmStateReady) {
        flag = TumoVgmWorkerFlagStart;
    } else if(state == TumoVgmStateSession) {
        flag = TumoVgmWorkerFlagStop;
    } else if(state != TumoVgmStateScanning) {
        flag = TumoVgmWorkerFlagRetry;
    }
    if(flag != 0) furi_thread_flags_set(app->worker_id, flag);
    return flag != 0;
}

static void tumovgm_serial_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    TumoVgmApp* app = context;
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            const uint8_t byte = furi_hal_serial_async_rx(handle);
            furi_stream_buffer_send(app->rx_stream, &byte, 1, 0);
        }
        furi_thread_flags_set(app->worker_id, TumoVgmWorkerFlagData);
    }
}

static bool tumovgm_serial_start(TumoVgmApp* app) {
    expansion_disable(app->expansion);
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial == NULL) {
        expansion_enable(app->expansion);
        return false;
    }
    furi_hal_serial_init(app->serial, TumoVgmBridgeBaud);
    furi_hal_serial_async_rx_start(app->serial, tumovgm_serial_callback, app, false);
    app->serial_started = true;
    return true;
}

static void tumovgm_serial_stop(TumoVgmApp* app) {
    if(app->serial_started) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        app->serial = NULL;
        app->serial_started = false;
    }
    expansion_enable(app->expansion);
}

static void tumovgm_discard_rx(TumoVgmApp* app, size_t count) {
    if(count >= app->rx_size) {
        app->rx_size = 0;
    } else {
        memmove(app->rx_buffer, app->rx_buffer + count, app->rx_size - count);
        app->rx_size -= count;
    }
}

static void tumovgm_drain_stream(TumoVgmApp* app) {
    furi_stream_buffer_reset(app->rx_stream);
    app->rx_size = 0;
    furi_thread_flags_clear(TumoVgmWorkerFlagData);
}

static uint16_t tumovgm_next_sequence(TumoVgmApp* app) {
    app->sequence++;
    if(app->sequence == 0) app->sequence++;
    return app->sequence;
}

static bool tumovgm_exchange(
    TumoVgmApp* app,
    const TumovgmFrame* request,
    TumovgmFrame* response,
    uint32_t timeout_ms) {
    tumovgm_drain_stream(app);
    size_t tx_size = 0;
    if(tumovgm_frame_encode(request, app->tx_buffer, sizeof(app->tx_buffer), &tx_size) !=
       TumovgmCodecStatusOk) {
        return false;
    }
    furi_hal_serial_tx(app->serial, app->tx_buffer, tx_size);
    furi_hal_serial_tx_wait_complete(app->serial);

    const uint32_t start = furi_get_tick();
    const uint32_t timeout_ticks = furi_ms_to_ticks(timeout_ms);
    while((uint32_t)(furi_get_tick() - start) < timeout_ticks) {
        app->rx_size += furi_stream_buffer_receive(
            app->rx_stream,
            app->rx_buffer + app->rx_size,
            sizeof(app->rx_buffer) - app->rx_size,
            0);
        while(app->rx_size > 0) {
            TumovgmFrame decoded;
            size_t consumed = 0;
            const TumovgmCodecStatus status =
                tumovgm_frame_decode(app->rx_buffer, app->rx_size, &decoded, &consumed);
            if(status == TumovgmCodecStatusNeedMore) break;
            if(status == TumovgmCodecStatusOk && decoded.sequence == request->sequence &&
               decoded.message == request->message) {
                *response = decoded;
                return true;
            }
            tumovgm_discard_rx(app, consumed > 0 ? consumed : 1);
        }

        const uint32_t elapsed = furi_get_tick() - start;
        if(elapsed >= timeout_ticks) break;
        const uint32_t flags = furi_thread_flags_wait(
            TumoVgmWorkerFlagData | TumoVgmWorkerFlagExit,
            FuriFlagWaitAny,
            timeout_ticks - elapsed);
        if(flags == (uint32_t)FuriFlagErrorTimeout) continue;
        if(flags & FuriFlagError) return false;
        if(flags & TumoVgmWorkerFlagExit) {
            app->exit_requested = true;
            return false;
        }
    }
    return false;
}

static bool tumovgm_probe_stock(TumoVgmApp* app) {
    furi_hal_serial_set_br(app->serial, TumoVgmStockBaud);
    tumovgm_drain_stream(app);
    const uint32_t start = furi_get_tick();
    const uint32_t timeout = furi_ms_to_ticks(TumoVgmStockProbeMs);
    uint8_t bytes[32];
    while((uint32_t)(furi_get_tick() - start) < timeout) {
        const size_t count = furi_stream_buffer_receive(app->rx_stream, bytes, sizeof(bytes), 0);
        for(size_t index = 0; index < count; index++) {
            if(bytes[index] == 0xF0) return true;
        }
        const uint32_t elapsed = furi_get_tick() - start;
        if(elapsed >= timeout) break;
        const uint32_t flags = furi_thread_flags_wait(
            TumoVgmWorkerFlagData | TumoVgmWorkerFlagExit,
            FuriFlagWaitAny,
            timeout - elapsed);
        if(flags == (uint32_t)FuriFlagErrorTimeout) continue;
        if(flags & FuriFlagError) return false;
        if(flags & TumoVgmWorkerFlagExit) {
            app->exit_requested = true;
            return false;
        }
    }
    return false;
}

static bool tumovgm_response_error(const TumovgmFrame* response, TumovgmError error) {
    return response->kind == TumovgmFrameKindError &&
           response->payload_length >= TumoVgmErrorPayloadSize &&
           tumovgm_read_u16(response->payload) == (uint16_t)error;
}

static void tumovgm_probe(TumoVgmApp* app) {
    memset(&app->device, 0, sizeof(app->device));
    tumovgm_set_state(app, TumoVgmStateScanning, "Checking UART...");
    furi_hal_serial_set_br(app->serial, TumoVgmBridgeBaud);
    furi_delay_ms(25);

    uint8_t hello_payload[TumoVgmHelloPayloadSize] = {0};
    hello_payload[0] = TumovgmRoleFlipper;
    tumovgm_write_u16(hello_payload + 2, TUMOVGM_PROTOCOL_MAX_PAYLOAD);
    const TumovgmFrame hello = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = TUMOVGM_PROTOCOL_MINOR,
        .kind = TumovgmFrameKindRequest,
        .sequence = tumovgm_next_sequence(app),
        .message = TumovgmMessageHello,
        .payload_length = sizeof(hello_payload),
        .payload = hello_payload,
    };
    TumovgmFrame response;
    if(!tumovgm_exchange(app, &hello, &response, TumoVgmExchangeTimeoutMs)) {
        if(app->exit_requested) return;
        const bool stock = tumovgm_probe_stock(app);
        if(app->exit_requested) return;
        tumovgm_set_state(
            app,
            stock ? TumoVgmStateStock : TumoVgmStateMissing,
            stock ? "Official firmware" : "Check module/power");
        return;
    }
    if(response.major != TUMOVGM_PROTOCOL_MAJOR ||
       tumovgm_response_error(&response, TumovgmErrorUnsupportedVersion)) {
        app->device.protocol_major = response.major;
        app->device.protocol_minor = response.minor;
        tumovgm_set_state(app, TumoVgmStateIncompatible, "Different protocol major");
        return;
    }
    if(response.kind != TumovgmFrameKindResponse ||
       response.payload_length != TumoVgmHelloPayloadSize ||
       response.payload[0] != TumovgmRoleVgm) {
        tumovgm_set_state(app, TumoVgmStateError, "Invalid HELLO response");
        return;
    }

    app->device.protocol_major = response.major;
    app->device.protocol_minor = response.payload[1];
    app->device.capabilities = tumovgm_read_u64(response.payload + 4);
    strlcpy(app->device.version, "TumoVGM v1.0", sizeof(app->device.version));
    strlcpy(app->device.commit, "unknown", sizeof(app->device.commit));

    if(app->device.protocol_minor >= 1) {
        const TumovgmFrame info = {
            .major = TUMOVGM_PROTOCOL_MAJOR,
            .minor = app->device.protocol_minor,
            .kind = TumovgmFrameKindRequest,
            .sequence = tumovgm_next_sequence(app),
            .message = TumovgmMessageDeviceInfo,
        };
        if(tumovgm_exchange(app, &info, &response, TumoVgmExchangeTimeoutMs) &&
           response.kind == TumovgmFrameKindResponse &&
           response.payload_length == TumoVgmDeviceInfoPayloadSize) {
            app->device.hardware_target = tumovgm_read_u16(response.payload);
            tumovgm_copy_fixed_ascii(
                app->device.version, sizeof(app->device.version), response.payload + 4, 24);
            tumovgm_copy_fixed_ascii(
                app->device.commit, sizeof(app->device.commit), response.payload + 28, 12);
            app->device.dirty = response.payload[40] != 0;
        }
    }
    tumovgm_set_state(app, TumoVgmStateReady, "Connected");
}

static void tumovgm_start_session(TumoVgmApp* app) {
    tumovgm_set_state(app, TumoVgmStateScanning, "Opening session...");
    uint8_t payload[TumoVgmSessionOpenPayloadSize] = {0};
    tumovgm_write_u32(payload + 8, TumoVgmLeaseMs);
    const TumovgmFrame request = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = app->device.protocol_minor,
        .kind = TumovgmFrameKindRequest,
        .sequence = tumovgm_next_sequence(app),
        .message = TumovgmMessageSessionOpen,
        .payload_length = sizeof(payload),
        .payload = payload,
    };
    TumovgmFrame response;
    if(tumovgm_exchange(app, &request, &response, TumoVgmExchangeTimeoutMs) &&
       response.kind == TumovgmFrameKindResponse &&
       response.payload_length == TumoVgmSessionOpenResponseSize) {
        app->device.session_id = tumovgm_read_u32(response.payload);
        tumovgm_set_state(app, TumoVgmStateSession, "Keepalive active");
    } else if(!app->exit_requested) {
        app->device.session_id = 0;
        tumovgm_set_state(app, TumoVgmStateError, "Open timed out");
    }
}

static bool tumovgm_stop_session(TumoVgmApp* app, uint32_t timeout_ms) {
    if(app->device.session_id == 0) return true;
    uint8_t payload[TumoVgmSessionClosePayloadSize];
    tumovgm_write_u32(payload, app->device.session_id);
    const TumovgmFrame request = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = app->device.protocol_minor,
        .kind = TumovgmFrameKindRequest,
        .sequence = tumovgm_next_sequence(app),
        .message = TumovgmMessageSessionClose,
        .payload_length = sizeof(payload),
        .payload = payload,
    };
    TumovgmFrame response;
    const bool success = tumovgm_exchange(app, &request, &response, timeout_ms) &&
                         response.kind == TumovgmFrameKindResponse;
    app->device.session_id = 0;
    tumovgm_set_state(
        app,
        success ? TumoVgmStateReady : TumoVgmStateError,
        success ? "Session closed" : "Close timed out");
    return success;
}

static bool tumovgm_ping(TumoVgmApp* app) {
    uint8_t payload[TumoVgmPingPayloadSize] = {0};
    tumovgm_write_u32(payload, furi_get_tick());
    const TumovgmFrame request = {
        .major = TUMOVGM_PROTOCOL_MAJOR,
        .minor = app->device.protocol_minor,
        .kind = TumovgmFrameKindRequest,
        .sequence = tumovgm_next_sequence(app),
        .message = TumovgmMessagePing,
        .payload_length = sizeof(payload),
        .payload = payload,
    };
    TumovgmFrame response;
    return tumovgm_exchange(app, &request, &response, TumoVgmExchangeTimeoutMs) &&
           response.kind == TumovgmFrameKindResponse &&
           response.payload_length == sizeof(payload) &&
           memcmp(response.payload, payload, sizeof(payload)) == 0;
}

static int32_t tumovgm_worker(void* context) {
    TumoVgmApp* app = context;
    app->worker_id = furi_thread_get_current_id();
    while(!tumovgm_serial_start(app)) {
        tumovgm_set_state(app, TumoVgmStateError, "USART is busy");
        const uint32_t flags = furi_thread_flags_wait(
            TumoVgmWorkerFlagExit | TumoVgmWorkerFlagRetry, FuriFlagWaitAny, FuriWaitForever);
        if(flags & TumoVgmWorkerFlagExit) return 0;
    }
    tumovgm_probe(app);

    while(!app->exit_requested) {
        const uint32_t timeout = app->device.state == TumoVgmStateSession ?
                                     furi_ms_to_ticks(TumoVgmKeepaliveMs) :
                                     FuriWaitForever;
        const uint32_t flags =
            furi_thread_flags_wait(TUMOVGM_WORKER_FLAGS, FuriFlagWaitAny, timeout);
        if(flags == (uint32_t)FuriFlagErrorTimeout) {
            if(app->device.state == TumoVgmStateSession && !tumovgm_ping(app) &&
               !app->exit_requested) {
                app->device.session_id = 0;
                tumovgm_set_state(app, TumoVgmStateError, "Module disconnected");
            }
            continue;
        }
        if(flags & FuriFlagError) continue;
        if(flags & TumoVgmWorkerFlagExit) break;
        if(flags & TumoVgmWorkerFlagRetry) tumovgm_probe(app);
        if(flags & TumoVgmWorkerFlagStart && app->device.state == TumoVgmStateReady)
            tumovgm_start_session(app);
        if(flags & TumoVgmWorkerFlagStop && app->device.state == TumoVgmStateSession)
            tumovgm_stop_session(app, TumoVgmExchangeTimeoutMs);
    }

    if(app->device.session_id != 0) tumovgm_stop_session(app, 200);
    tumovgm_serial_stop(app);
    return 0;
}

static TumoVgmApp* tumovgm_app_alloc(void) {
    TumoVgmApp* app = malloc(sizeof(*app));
    memset(app, 0, sizeof(*app));
    app->gui = furi_record_open(RECORD_GUI);
    app->expansion = furi_record_open(RECORD_EXPANSION);
    app->rx_stream = furi_stream_buffer_alloc(TumoVgmRxStreamSize, 1);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    app->view = view_alloc();
    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoVgmViewModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, tumovgm_draw_callback);
    view_set_input_callback(app->view, tumovgm_input_callback);
    view_set_previous_callback(app->view, tumovgm_previous_callback);
    view_dispatcher_add_view(app->view_dispatcher, 0, app->view);

    app->device.state = TumoVgmStateScanning;
    strlcpy(app->device.detail, "Starting worker...", sizeof(app->device.detail));
    tumovgm_refresh_view(app);
    app->worker = furi_thread_alloc_ex(TAG "Worker", 6 * 1024, tumovgm_worker, app);
    furi_thread_start(app->worker);
    app->worker_id = furi_thread_get_id(app->worker);
    return app;
}

static void tumovgm_app_free(TumoVgmApp* app) {
    if(app->worker) {
        furi_thread_flags_set(app->worker_id, TumoVgmWorkerFlagExit);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    view_dispatcher_remove_view(app->view_dispatcher, 0);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_stream_buffer_free(app->rx_stream);
    furi_record_close(RECORD_EXPANSION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumovgm_bridge_app(void* context) {
    UNUSED(context);
    TumoVgmApp* app = tumovgm_app_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, 0);
    view_dispatcher_run(app->view_dispatcher);
    tumovgm_app_free(app);
    return 0;
}
