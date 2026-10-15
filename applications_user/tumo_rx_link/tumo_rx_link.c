#include "rx_link.h"
#include "rx_link_view.h"
#include <applications/system/subghz_app_radio.h>
#include <expansion/expansion.h>
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t frequencies[] = {315000000, 433920000, 868350000};
static const FuriHalSubGhzPreset presets[] = {
    FuriHalSubGhzPresetOok650Async,
    FuriHalSubGhzPreset2FSKDev238Async};

typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMutex* mutex;
    FuriMessageQueue* input;
    FuriMessageQueue* edges;
    FuriStreamBuffer* uart_rx;
    FuriHalSerialHandle* serial;
    bool serial_initialized;
    bool serial_receiving;
    bool radio_receiving;
    SubGhzAppRadio radio;
    RxLinkClient client;
    RxLinkParser parser;
    RxLinkViewModel display;
    RxLinkViewModel state;
    volatile uint32_t dropped_edges;
    volatile uint32_t dropped_uart_bytes;
    uint16_t sequence;
    uint32_t last_query;
    uint32_t last_uart;
} RxLinkApp;

static void
    rx_link_uart_irq(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    RxLinkApp* app = context;
    if(event & FuriHalSerialRxEventData) {
        const uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->uart_rx, &byte, 1, 0) != 1 &&
           app->dropped_uart_bytes < UINT32_MAX)
            app->dropped_uart_bytes++;
    }
}

static void rx_link_radio_irq(bool level, uint32_t duration, void* context) {
    RxLinkApp* app = context;
    uint32_t edge = duration | (level ? 0x80000000u : 0);
    if(!duration || duration > RX_LINK_DURATION_MAX ||
       furi_message_queue_put(app->edges, &edge, 0) != FuriStatusOk) {
        if(app->dropped_edges < UINT32_MAX) app->dropped_edges++;
    }
}

static void rx_link_send(RxLinkApp* app, const RxLinkFrame* frame) {
    uint8_t wire[RX_LINK_FRAME_MAX];
    size_t size = rx_link_encode(frame, wire, sizeof(wire));
    if(app->serial_initialized && size) {
        furi_hal_serial_tx(app->serial, wire, size);
        furi_hal_serial_tx_wait_complete(app->serial);
    }
}

static void rx_link_stop_io(RxLinkApp* app) {
    // Join DMA/interrupt producers before releasing their queues, UART or radio lease.
    if(app->radio_receiving) {
        subghz_devices_stop_async_rx(app->radio.device);
        app->radio_receiving = false;
    }
    if(app->serial_initialized && app->client.session) {
        RxLinkFrame stop = {.type = RxLinkStop, .length = 4};
        rx_link_write32(stop.data, app->client.session);
        rx_link_send(app, &stop);
    }
    if(app->serial_receiving) {
        furi_hal_serial_async_rx_stop(app->serial);
        app->serial_receiving = false;
    }
    if(app->serial_initialized) {
        furi_hal_serial_deinit(app->serial);
        app->serial_initialized = false;
    }
    if(app->serial) {
        furi_hal_serial_control_release(app->serial);
        app->serial = NULL;
    }
    subghz_app_radio_release(&app->radio);
    rx_link_client_stop(&app->client);
}

static void rx_link_fail(RxLinkApp* app, RxLinkError error) {
    rx_link_stop_io(app);
    app->state.phase = RxUiError;
    app->state.error = error;
}

static void rx_link_hello(RxLinkApp* app) {
    RxLinkFrame hello = {.type = RxLinkHello, .length = 8};
    memcpy(hello.data, "RXA1", 4);
    rx_link_write32(hello.data + 4, app->client.session);
    rx_link_send(app, &hello);
}

static void rx_link_start(RxLinkApp* app, uint32_t now) {
    rx_link_stop_io(app);
    memset(&app->parser, 0, sizeof(app->parser));
    memset(&app->state.snapshot, 0, sizeof(app->state.snapshot));
    furi_message_queue_reset(app->edges);
    furi_stream_buffer_reset(app->uart_rx);
    app->dropped_edges = app->dropped_uart_bytes = 0;
    app->sequence = 0;
    app->state.page = 0;
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial) {
        rx_link_fail(app, RxErrorUart);
        return;
    }
    if(!subghz_app_radio_acquire(&app->radio, "Tumo RX Link", frequencies[app->state.frequency])) {
        rx_link_fail(app, RxErrorRadio);
        return;
    }
    if(!app->radio.external) {
        rx_link_fail(app, RxErrorModule);
        return;
    }
    // Keep the broker-owned Module One supply on while the ESP32 boots.
    furi_hal_serial_init(app->serial, RX_LINK_BAUD);
    app->serial_initialized = true;
    furi_hal_serial_configure_framing(
        app->serial, FuriHalSerialDataBits8, FuriHalSerialParityNone, FuriHalSerialStopBits1);
    furi_hal_serial_async_rx_start(app->serial, rx_link_uart_irq, app, true);
    app->serial_receiving = true;
    uint32_t session = furi_hal_random_get();
    rx_link_client_begin(&app->client, session ? session : 1, now);
    app->last_query = app->last_uart = now;
    app->state.phase = RxUiWaiting;
    app->state.error = RxErrorNone;
    rx_link_hello(app);
}

static void rx_link_begin_radio(RxLinkApp* app) {
    if(!subghz_app_radio_is_connected(&app->radio)) {
        rx_link_fail(app, RxErrorRadioLost);
        return;
    }
    subghz_devices_load_preset(app->radio.device, presets[app->state.modulation], NULL);
    subghz_devices_set_frequency(app->radio.device, frequencies[app->state.frequency]);
    subghz_devices_flush_rx(app->radio.device);
    if(!subghz_radio_broker_set_state(
           app->radio.broker, &app->radio.lease, SubGhzRadioBrokerStateRx)) {
        rx_link_fail(app, RxErrorRadio);
        return;
    }
    subghz_devices_start_async_rx(app->radio.device, rx_link_radio_irq, app);
    app->radio_receiving = true;
    subghz_devices_set_rx(app->radio.device);
    app->state.phase = RxUiLive;
}

static void rx_link_poll(RxLinkApp* app, uint32_t now) {
    if(!app->client.waiting && !app->client.connected) return;
    if((uint32_t)(now - app->last_uart) > 100 && app->parser.used) {
        app->parser.used = app->parser.expected = 0;
        if(app->parser.errors < UINT32_MAX) app->parser.errors++;
    }
    uint8_t bytes[256];
    size_t count = furi_stream_buffer_receive(app->uart_rx, bytes, sizeof(bytes), 0);
    if(count) app->last_uart = now;
    for(size_t i = 0; i < count; i++) {
        RxLinkFrame frame;
        if(rx_link_feed(&app->parser, bytes[i], &frame)) {
            unsigned event = rx_link_client_accept(&app->client, &frame, now);
            if(event == RxLinkClientReady) rx_link_begin_radio(app);
            if(event == RxLinkClientSnapshot) app->state.snapshot = app->client.snapshot;
        }
    }
    if(!app->client.waiting && !app->client.connected) return;
    if(rx_link_client_expired(&app->client, now)) {
        rx_link_fail(app, app->client.waiting ? RxErrorFirmware : RxErrorDisconnected);
        return;
    }
    if(app->radio_receiving) {
        RxLinkFrame batch = {.type = RxLinkEdges, .sequence = app->sequence, .length = 8};
        rx_link_write32(batch.data, app->client.session);
        rx_link_write32(batch.data + 4, app->dropped_edges);
        uint32_t edge;
        while(batch.length <= RX_LINK_PAYLOAD_MAX - 4 &&
              furi_message_queue_get(app->edges, &edge, 0) == FuriStatusOk) {
            rx_link_write32(batch.data + batch.length, edge);
            batch.length += 4;
        }
        if(batch.length > 8) {
            rx_link_send(app, &batch);
            app->sequence++;
        }
        if(app->client.snapshot.count >= RX_LINK_SAMPLE_LIMIT) {
            rx_link_stop_io(app);
            app->state.phase = RxUiStopped;
            return;
        }
    }
    if((uint32_t)(now - app->last_query) >= 250) {
        app->last_query = now;
        if(!subghz_app_radio_is_connected(&app->radio)) {
            rx_link_fail(app, RxErrorRadioLost);
            return;
        }
        if(app->client.waiting) {
            rx_link_hello(app);
        } else if(app->client.connected) {
            RxLinkFrame query = {.type = RxLinkQuery, .length = 4};
            rx_link_write32(query.data, app->client.session);
            rx_link_send(app, &query);
        }
    }
}

static void rx_link_draw_callback(Canvas* canvas, void* context) {
    RxLinkApp* app = context;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    RxLinkViewModel model = app->display;
    furi_mutex_release(app->mutex);
    rx_link_draw(canvas, &model);
}

static void rx_link_input(InputEvent* event, void* context) {
    RxLinkApp* app = context;
    furi_message_queue_put(app->input, event, 0);
}

int32_t tumo_rx_link_app(void* context) {
    UNUSED(context);
    RxLinkApp* app = calloc(1, sizeof(*app));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->edges = furi_message_queue_alloc(512, sizeof(uint32_t));
    app->uart_rx = furi_stream_buffer_alloc(1024, 1);
    app->state.frequency = 1;
    app->display = app->state;
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);
    app->gui = furi_record_open(RECORD_GUI);
    app->viewport = view_port_alloc();
    view_port_draw_callback_set(app->viewport, rx_link_draw_callback, app);
    view_port_input_callback_set(app->viewport, rx_link_input, app);
    gui_add_view_port(app->gui, app->viewport, GuiLayerFullscreen);
    bool running = true;
    while(running) {
        InputEvent event;
        if(furi_message_queue_get(app->input, &event, 10) == FuriStatusOk &&
           event.type == InputTypeShort) {
            if(event.key == InputKeyBack) {
                running = false;
                break;
            }
            if(event.key == InputKeyOk) {
                if(app->client.waiting || app->client.connected) {
                    rx_link_stop_io(app);
                    app->state.phase = RxUiStopped;
                } else
                    rx_link_start(app, furi_get_tick());
            } else if(app->state.phase == RxUiIdle || app->state.phase == RxUiError) {
                if(event.key == InputKeyUp) app->state.frequency = (app->state.frequency + 1) % 3;
                if(event.key == InputKeyDown)
                    app->state.frequency = (app->state.frequency + 2) % 3;
                if(event.key == InputKeyLeft || event.key == InputKeyRight)
                    app->state.modulation ^= 1;
                app->state.phase = RxUiIdle;
            } else if(app->state.phase == RxUiLive || app->state.phase == RxUiStopped) {
                if(event.key == InputKeyRight) app->state.page = (app->state.page + 1) % 3;
                if(event.key == InputKeyLeft) app->state.page = (app->state.page + 2) % 3;
            }
        }
        rx_link_poll(app, furi_get_tick());
        app->state.local_drops = app->dropped_edges;
        app->state.uart_errors = app->parser.errors > UINT32_MAX - app->dropped_uart_bytes ?
                                     UINT32_MAX :
                                     app->parser.errors + app->dropped_uart_bytes;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->display = app->state;
        furi_mutex_release(app->mutex);
        view_port_update(app->viewport);
    }
    rx_link_stop_io(app);
    view_port_enabled_set(app->viewport, false);
    gui_remove_view_port(app->gui, app->viewport);
    view_port_free(app->viewport);
    furi_record_close(RECORD_GUI);
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
    furi_stream_buffer_free(app->uart_rx);
    furi_message_queue_free(app->edges);
    furi_message_queue_free(app->input);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
